<span class="chapter-kicker">CHAPITRE 14 / RETROUVER UNE SYNTAXE</span>
# Référence de tous les mots-clés

## Objectifs

- retrouver les 37 mots-clés réservés de la version en développement de Janus ;
- comprendre leur utilité et leur contexte valide ;
- ne pas confondre mots-clés, types primitifs, builtins et opérateurs.

Un mot-clé est réservé par le lexer et ne peut pas servir de nom de variable, de fonction ou de type. Cette page suit directement la liste du compilateur.

## Modules et fonctions

| Mot-clé | Utilité | Exemple ou règle |
| --- | --- | --- |
| `module` | donne un nom qualifié au fichier | doit précéder les imports et déclarations |
| `import` | rend les exports d’un module disponibles | `import std.array` |
| `as` | donne un alias local à un module ou symbole importé | `import std.fs as fs` ou `Color as ThemeColor` |
| `def` | déclare une fonction ou méthode typée | `def answer() : int { return 42 }` |
| `tailrec` | garantit et rend explicite une récursion terminale | `tailrec def loop(n : int) : int { ... }` |
| `extern` | déclare une fonction native sans corps Janus | `extern def native_call() : int` |
| `return` | quitte une fonction, avec sa valeur si nécessaire | `return 0` ; le type doit correspondre |
| `pure` | garantit transitivement l'absence d'effets observables | `pure def twice(x : int) : int { return x * 2 }` |

`module` et `import` acceptent un point-virgule facultatif. `extern` se combine avec `def` ; une déclaration externe variadique peut terminer ses paramètres par `...`.

Toute fonction ou méthode appartenant à un cycle récursif direct ou mutuel
entièrement terminal et compatible avec la garantie backend `musttail` doit
porter `tailrec`. Une récursion ordinaire, un cycle mixte ou un cycle dont le
retour ou le nettoyage empêche `musttail` reste légal sans annotation. Le
compilateur rejette alors `tailrec`, comme il le rejette sur une déclaration non
récursive. La grammaire canonique est :
`[private] [const] tailrec def` au niveau module et
`[private|internal] [borrow|consume] tailrec def` dans une classe. Les groupes
entre crochets sont facultatifs et conservent cet ordre.

## Déclarations de types et capacités

| Mot-clé | Utilité | Exemple ou règle |
| --- | --- | --- |
| `struct` | définit un agrégat à sémantique de valeur | peut devenir propriétaire selon ses champs |
| `class` | définit un objet possédé, alloué avec `new` | ne peut jamais dériver `Copy` |
| `enum` | définit un ensemble de variantes, avec payloads facultatifs | se traite généralement avec `match` |
| `trait` | définit un contrat de méthodes | les méthodes n’ont pas de corps |
| `type` | déclare un type associé dans un trait ou son implémentation | `type Item` ou `type Item = int` |
| `extends` | annonce les traits implémentés par une classe | précède `derives` et le corps de classe |
| `derives` | génère une ou plusieurs capacités structurelles | seulement `Copy`, `Equality`, `Hashing`, `Debug` |

Exemple combiné :

```janus
trait Named {
    def name() : string
}

class User(val identifier : int, val label : string)
extends Named
derives Equality, Hashing, Debug {
    def name() : string {
        return label
    }
}
```

`Hashing` exige `Equality`. Les capacités sont intrinsèques et sensibles à la casse ; elles ne sont pas des mots-clés séparés. En 0.22.0, seuls les classes implémentent les traits utilisateur avec `extends` ; les structs et enums peuvent utiliser `derives`, mais pas `extends`.

`extend` introduit un bloc de méthodes d'extension, mais reste un mot
contextuel afin de ne pas rendre invalides les identifiants existants.

## Liaisons et visibilité

| Mot-clé | Utilité | Exemple ou règle |
| --- | --- | --- |
| `const` | crée une valeur évaluée pendant la compilation | peut dépendre uniquement d'autres constantes admissibles |
| `val` | crée une liaison non réassignable | l’objet pointé peut néanmoins avoir des champs `var` |
| `var` | crée une liaison réassignable | une locale peut être déclarée avant initialisation |
| `private` | limite une déclaration au module, ou un membre à sa classe | valide au niveau supérieur et dans les types |
| `internal` | ouvre un membre aux déclarations du même module | uniquement pour les champs et méthodes de classe |

`val` signifie « la liaison ne change pas », pas « toute la valeur est profondément immuable ». Les paramètres `val` ou `var` du constructeur d’une classe/structure deviennent des champs ; un paramètre sans l’un de ces mots reste un simple paramètre d’initialisation.

## Construction, propriété et destruction

| Mot-clé | Utilité | Exemple ou règle |
| --- | --- | --- |
| `new` | construit une classe, une structure ou une valeur nécessitant un constructeur | une classe obtenue est propriétaire |
| `move` | transfère explicitement une liaison locale propriétaire | la source devient inutilisable |
| `borrow` | crée un alias observant ou qualifie un pointeur externe | ne transfère jamais la propriété |
| `consume` | marque une méthode qui consomme son receveur `this` | s’écrit avant `def` |
| `delete` | détruit une valeur propriétaire | destruction récursive des agrégats |
| `defer` | reporte une expression à la sortie de la portée | les actions sont exécutées en ordre inverse |
| `destructor` | définit le bloc de nettoyage d’une classe | s’exécute lors de `delete` avant libération |

```janus
class Resource(val value : int) {
    consume def take() : int {
        val result : int = value
        delete this
        return result
    }

    destructor {
        println("nettoyage")
    }
}

def main() : int {
    val resource : Resource = new Resource(42)
    return resource.take() - 42
}
```

N’écrivez `move` que lorsqu’un transfert est requis. Il est refusé pour un type `Copy`, une expression temporaire ou un champ extrait indépendamment de son agrégat.

## Conditions et motifs

| Mot-clé | Utilité | Exemple ou règle |
| --- | --- | --- |
| `if` | exécute une branche si un booléen est vrai | la condition doit être `bool` |
| `else` | fournit la branche alternative | `else if` forme une chaîne de décisions |
| `match` | sélectionne une branche selon la variante d’un enum | peut produire une valeur |
| `true` | littéral booléen vrai | type `bool` |
| `false` | littéral booléen faux | type `bool` |

```janus
enum State { Ready(int), Waiting }

def value(state : State) : int {
    if false {
        return -1
    } else {
        return match state {
            Ready(number) => number,
            Waiting => 0
        }
    }
}
```

Un `match` sur un enum propriétaire exige `match move value`. Les branches transfèrent alors les payloads propriétaires avec `move`.

## Boucles

| Mot-clé | Utilité | Exemple ou règle |
| --- | --- | --- |
| `while` | répète tant qu’une condition reste vraie | `while condition { ... }` |
| `for` | parcourt un itérateur ou un `Iterable` | `for item in values { ... }` |
| `in` | sépare la liaison de la source dans un `for` | n’est pas un opérateur d’appartenance général |
| `break` | quitte la boucle la plus proche | exécute les `defer` de la portée quittée |
| `continue` | passe à l’itération suivante | exécute aussi les nettoyages de portée nécessaires |

Pour des éléments propriétaires, utilisez explicitement un parcours consommant tel que `values.intoIterator()` ; un `for` n’invente jamais une copie.

## Ce qui n’est pas un mot-clé

Les noms suivants sont importants, mais appartiennent à d’autres catégories :

- `int`, `string`, `bool`, `Unit`, `usize`, `Ptr[T]` sont des types intégrés ;
- `Copy`, `Equality`, `Hashing`, `Debug` sont des capacités reconnues par `derives` ;
- `owned` est un qualificateur contextuel de retour `extern`, pas un mot-clé réservé ;
- `print`, `println`, `debug`, `alloc`, `free` sont des fonctions ou builtins ;
- `Option` et `Result` sont des enums de la bibliothèque standard ;
- `this` est le receveur disponible dans une méthode, pas un mot-clé lexical réservé.

## Opérateurs et ponctuation essentiels

| Syntaxe | Sens |
| --- | --- |
| `+ - * / %` | arithmétique |
| `== != < <= > >=` | comparaison |
| `! && \|\|` | négation, et/ou court-circuités |
| `=` | initialisation ou affectation |
| `=>` | type/littéral de fonction et branche de `match` |
| `?` | propagation d’une `Option` ou d’un `Result` compatible |
| `[T]` | paramètres/arguments génériques |
| `T <: Trait` | contrainte générique |
| `&` | combinaison de contraintes de traits |
| `...` | fin variadique d’une déclaration externe |
| `.` | accès membre ou qualification de module |

## Exercice

Classez `move`, `borrow`, `owned`, `Copy`, `Option`, `?`, `consume` et `Ptr` selon leur catégorie et résumez leur rôle.

??? success "Correction"
    - mots-clés : `move` transfère une ressource ; `borrow` observe sans posséder ; `consume` marque une méthode qui prend `this` ;
    - qualificateur contextuel : `owned` annonce qu’un pointeur retourné par `extern` appartient à Janus ;
    - capacité : `Copy` autorise une duplication implicite sûre ;
    - type de stdlib : `Option[T]` représente une présence ou une absence ;
    - opérateur : `?` propage l’absence ou l’erreur ;
    - type intégré : `Ptr[T]` représente un pointeur bas niveau.

<div class="lesson-nav"><a href="../13-projets-tests-outils/">← Projets, tests et outils</a><a href="../15-projet-final/">Projet final →</a></div>
