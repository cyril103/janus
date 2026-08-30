# Méthodes d'extension statiques

Statut : RFC expérimentale pour l'issue #290.

## Objectif

Une extension attache des opérations à un type existant sans modifier sa
représentation, son ABI ou sa table de méthodes natives. Les appels restent
résolus statiquement et monomorphisés.

```janus
extend[T] Option[T] {
    consume def map[U](scoped transform : (T) => U) : Option[U] {
        return match move this {
            Some(value) => Option.Some[U](transform(move value)),
            None => Option.None[U]()
        }
    }
}
```

Le receveur implicite se nomme `this`. Les paramètres entre `extend` et le
type cible sont les paramètres génériques de l'extension. Ils sont explicites
pour distinguer sans heuristique `T`, paramètre abstrait, d'un type nominal
concret utilisé comme argument du type cible.

## Résolution et visibilité

Une extension est candidate lorsque :

1. son type cible s'unifie exactement avec le type statique du receveur ;
2. son nom de méthode correspond ;
3. elle appartient au module courant ou son module est importé sans alias ;
4. elle est publique, sauf lorsqu'elle appartient au module courant ;
5. ses contraintes génériques sont satisfaites.

Les imports qualifiés n'activent pas implicitement les extensions. Une méthode
native gagne toujours sur les extensions. En l'absence de méthode native,
plusieurs extensions applicables produisent un diagnostic d'ambiguïté :
Janus n'en choisit jamais une selon l'ordre des imports.

Une extension ne peut pas remplacer une méthode native et deux méthodes de
même nom sont interdites dans un même bloc. La version initiale ne fournit pas
de surcharge par signature.

## Règle de cohérence

Une extension publique doit être déclarée dans le module qui définit le type
cible. Un autre module peut étendre un type importé seulement avec une
extension `private`; cette extension reste donc locale à ce module. Cette
règle permet l'adaptation locale tout en empêchant les extensions orphelines
publiques et les changements de comportement transitifs.

Les types intégrés sont considérés comme définis par le langage et ne peuvent
recevoir que des extensions privées. Les pointeurs suivent la même règle.

## Propriété du receveur

Chaque méthode d'extension déclare explicitement le contrat de son receveur :

- `borrow def` reçoit un emprunt partagé et ne peut ni déplacer ni muter le
  receveur ;
- `borrow var def` reçoit un emprunt mutable ;
- `consume def` prend possession du receveur, qui devient inutilisable après
  l'appel.

Une méthode d'extension sans l'un de ces trois modificateurs est rejetée. Les
paramètres ordinaires conservent les règles existantes de `borrow`, `scoped`,
transfert, retour emprunté, `defer` et destruction.

## Abaissement et ABI

Le compilateur abaisse chaque méthode d'extension vers une fonction statique
dont le premier paramètre est le receveur implicite. Le symbole inclut le
module, le type cible, le nom de méthode et les spécialisations génériques.
Il n'existe ni vtable, ni recherche dynamique, ni champ supplémentaire dans le
type étendu.

Un appel `value.method(args)` est abaissé comme l'appel de cette fonction avec
`value` en premier argument. `borrow` utilise les conventions d'emprunt
existantes pour les structs et enums; `consume` transmet la valeur possédée.

## Outillage et évolution

Le formateur traite `extend` comme un bloc de premier niveau. Le LSP expose les
méthodes visibles après `.` et relie définition, hover et références au bloc
d'extension. La documentation publique liste les extensions sous le type
cible sans les présenter comme des membres ABI.

La première version couvre les classes, structs et enums. Le support des types
intégrés et des pointeurs pourra être ajouté sans changer les règles de
résolution. Les extensions dynamiques, la substitution d'une méthode native et
les surcharges restent hors périmètre.
