# Contrat `pure def`

Statut : accepté pour la version en développement. Ce document est normatif
pour le vérificateur d'effets ; aucune optimisation ne peut supposer davantage
que les règles décrites ici.

## But et syntaxe

`pure def` décrit une fonction exécutable dont le résultat et la terminaison ne
dépendent que de ses arguments, sans effet observable sur le reste du
programme. Le contrat peut qualifier une fonction, une méthode, une signature
de trait ou une déclaration FFI :

```janus
pure def twice(value : int) : int { return value * 2 }
pure extern def nativeAbs(value : int) : int

class Box(val value : int) {
    pure borrow def read() : int { return value }
}
```

Un callback pur porte aussi l'effet dans son type :

```janus
pure def apply(action : pure (int) => int, value : int) : int {
    return action(value)
}
```

Un callback `(int) => int` ordinaire ne peut pas être appelé depuis une
fonction pure. La pureté fait partie de l'identité publique du type et de la
signature indexée. Une implémentation de méthode de trait doit conserver le
contrat de pureté annoncé par le trait.

## Effets autorisés

Une fonction pure peut effectuer des calculs déterministes, lire ses arguments
et les globales immuables, créer et modifier ses seules liaisons locales,
allouer avec `new`, détruire une valeur locale, appeler une autre fonction pure
ou `const def`, et instancier des fonctions génériques. Les cycles récursifs
purs sont analysés comme un graphe : un nœud `Visiting` ferme le cycle et
garantit la terminaison de l'analyse.

L'allocation fraîche est admise. L'identité d'adresse n'est pas une observation
préservée par le contrat : la comparaison structurelle explicite reste une
observation de la valeur, tandis qu'un cast de référence vers un entier ou une
FFI qui révèle une adresse sort du sous-langage pur. Une valeur allouée peut
être retournée ; elle ne doit pas permettre de retrouver ou modifier un état
préexistant caché.

`panic` est un résultat de terminaison déterministe autorisé. Il ne devient pas
une valeur et reste soumis aux diagnostics de nettoyage habituels. Le contrat
ne promet donc pas une fonction totale.

## Effets interdits

Le compilateur rejette, directement ou à travers une chaîne d'appels :

- la lecture ou l'écriture d'une globale mutable ;
- la mutation d'un argument, d'un emprunt mutable, d'un receveur ou de toute
  valeur visible par l'appelant ;
- l'appel d'une fonction ou méthode sans contrat `pure`/`const` ;
- l'appel d'un callback dont le type n'est pas `pure Function` ;
- une FFI non annotée `pure`, donc notamment les I/O, l'heure et l'aléatoire ;
- la destruction d'une valeur qui n'est pas locale à l'appel.

`borrow def` est un contrat de propriété, pas un contrat d'effets : une méthode
empruntée doit porter séparément `pure` avant de pouvoir être appelée. Lire un
argument `borrow` partagé est permis. Un paramètre `borrow var` et une méthode
`consume` sont incompatibles avec `pure` parce qu'ils autorisent un effet
visible chez l'appelant.

Une déclaration `pure extern def` constitue une frontière de confiance. Son
auteur garantit que l'implémentation native respecte exactement ce document ;
elle convient aux fonctions mathématiques déterministes, pas aux wrappers qui
consultent `errno`, l'environnement, une horloge ou un état interne mutable.

## Relation avec `const def`

Toute `const def` satisfait le noyau de pureté, mais ajoute les contraintes de
l'évaluateur de compilation : opérations et types constants admissibles,
liaisons locales `const`, budget de pas/mémoire et appels exclusivement vers
d'autres `const def`. Une `pure def` peut allouer et exécuter les constructions
runtime autorisées ; elle n'est donc pas automatiquement évaluable pendant la
compilation. Une `pure def` peut appeler une `const def`, jamais l'inverse.

## Diagnostics, modules et optimisation

Le vérificateur mémorise trois états (`Unvisited`, `Visiting`, `Complete`) par
déclaration et joint au diagnostic la chaîne d'appels pure qui mène au premier
effet interdit. Les annotations de fonctions et de types sont incluses dans le
contrat public et le cache incrémental.

Cette première version n'active volontairement ni mémoïsation, ni élimination
d'appels, ni évaluation constante d'une `pure def`. Ces optimisations exigeront
des tests d'équivalence séparés, notamment autour de l'allocation, de la
panique, de l'épuisement mémoire et des frontières FFI.
