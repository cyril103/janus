# Partage immutable `Shared[T]`

Statut : accepté et implémenté pour la prochaine version de Janus.

Cette décision introduit `std.shared.Shared[T]`, un handle à comptage de
références non atomique. Elle complète la propriété unique et les emprunts
lexicaux : le partage reste explicite, l'accès au contenu est exclusivement
immutable et la libération demeure déterministe.

## Problème

Une valeur propriétaire Janus ne peut normalement avoir qu'un propriétaire.
Ce modèle empêche les doubles destructions, mais une liste persistante, un AST
ou une configuration immutable devrait pouvoir réutiliser un sous-graphe sans
le copier profondément. Un simple emprunt ne convient pas : sa région ne peut
pas dépasser celle de son propriétaire et un objet contenant un emprunt ne peut
pas s'échapper librement.

`Shared[T]` fournit donc plusieurs propriétaires d'un même contenu, sans rendre
`T` mutable et sans introduire de ramasse-miettes.

## Stratégies comparées

| Stratégie | Avantages | Limites | Décision |
| --- | --- | --- | --- |
| classe stdlib, pointeurs privés et ABI runtime C | petite surface compilateur, monomorphisation et destruction de `T` déjà disponibles, politique lisible en Janus | deux allocations de contrôle, confiance dans le module privilégié et ses shims natifs | retenue |
| type intrinsèque connu de l'ownership checker | représentation et diagnostics entièrement contrôlés, optimisations plus directes | nouvelle catégorie dans le parseur, le type checker, l'ABI, le codegen, le LSP et tous les outils | différée tant que l'API n'exige pas ces optimisations |
| dérivation/capacité de partage immutable | intégration structurelle et ergonomie possible pour de nombreux types | transforme implicitement la propriété, ne définit pas seule l'identité ni la gestion des cycles, risque de clonage caché | rejetée pour la première version |

La solution retenue est une classe propriétaire ordinaire pour le compilateur.
Son constructeur de pointeurs est `internal`; les fonctions natives privées
`janus_shared_alias`, `janus_shared_forget` et `janus_shared_retain` forment la
frontière privilégiée qui permet au module de représenter plusieurs handles.
Aucun nouvel opérateur de copie implicite n'est ajouté au langage.

## API et règles de propriété

La création transfère une valeur possédée :

```janus
import std.shared

val first : Shared[Document] = share[Document](move document)
val second : Shared[Document] = first.clone()
defer delete first
defer delete second
```

Les règles sont les suivantes :

1. `share(value)` consomme `value` pour initialiser une allocation unique ;
2. `Shared[T]` ne satisfait pas `Copy`; une affectation ordinaire suit donc les
   règles de propriété des classes et un transfert exige `move` ;
3. `clone()` est une observation du handle, incrémente le compteur puis retourne
   un nouveau propriétaire ;
4. `get()` retourne `borrow T`, ancré au handle observé ; ce résultat ne peut ni
   être muté, ni déplacé, ni détruit, ni survivre au handle ;
5. `delete handle` décrémente le compteur ; les handles restants continuent
   d'observer la même identité ;
6. le passage de un à zéro déplace `T` hors de son stockage, libère les blocs de
   contrôle, puis détruit `T` exactement une fois ;
7. comme pour les autres classes Janus, la destruction du handle est explicite
   avec `delete` ou `defer delete`.

`strongCount()` expose une photographie informative du compteur. `isUnique()`
ne confère aucune capacité mutable, même lorsqu'il retourne `true`.
`isSame(other)` compare l'identité de l'allocation, pas l'égalité structurelle
des contenus. Aucune mutation, extraction propriétaire ou conversion en
pointeur public n'est fournie.

## Représentation et ABI

Chaque handle contient deux pointeurs privés : un bloc aligné pour un `T`
initialisé et un bloc contenant un `usize` initialisé à un. Le compteur compte
les handles forts, pas les emprunts temporaires produits par `get()`.

La représentation de la classe et les trois symboles runtime sont internes à
l'implémentation Janus. Ils ne constituent pas une ABI C stable. Une FFI reçoit
uniquement un emprunt sur `T` pendant un appel borné, ou une représentation C
créée explicitement par l'application ; elle ne doit ni retenir un pointeur de
contenu, ni fabriquer, cloner ou détruire un handle. Transmettre un `Shared[T]`
brut à du code natif est hors contrat.

Deux allocations séparées évitent d'avoir à calculer dans la stdlib le padding
entre le compteur et un `T` générique. Une représentation compacte pourra être
adoptée plus tard sans changer l'API source.

## Overflow, panique et destruction

Le compteur est un `usize`. `clone()` appelle une opération runtime qui vérifie
`usize::MAX` avant l'incrément. À saturation, le compteur reste inchangé et
`clone()` déclenche `panic("Shared reference count overflow")`; aucun nouveau
handle n'est créé et le handle initial reste valide.

À la dernière libération, le compteur passe à zéro avant l'appel du destructeur
de `T`. Le contenu est d'abord déplacé, puis les blocs `T` et compteur sont
libérés, enfin le destructeur de la valeur est exécuté. Une panique de ce
destructeur ne peut donc pas faire libérer le contenu une seconde fois et ne
laisse pas les blocs de contrôle alloués. La résurrection depuis un destructeur
n'est pas prise en charge : il n'existe plus de handle fort valide lorsque le
compteur vaut zéro.

L'échec d'une des allocations de `share()` libère les allocations déjà
obtenues, détruit la valeur entrante exactement une fois, puis déclenche une
panique. L'incrément réussi précède la construction du nouveau handle; comme
les allocations de classes suivent le contrat général du runtime Janus, un
échec fatal de cette allocation ne reprend pas l'exécution.

## Cycles et concurrence

Le comptage fort ne collecte pas les cycles. Un graphe dans lequel chaque nœud
reste accessible seulement depuis un autre `Shared` du même cycle fuit par
conception. L'API ne prétend pas diagnostiquer ces cycles. Une future primitive
Weak[T], avec un
second compteur et une opération `upgrade`, est reporté à un ticket distinct.

Le compteur n'est pas atomique. Lire, cloner ou détruire le même contrôle depuis
plusieurs threads sans synchronisation externe constitue un usage hors contrat.
AtomicShared[T] aura une ABI et des ordres mémoire propres s'il est ajouté ;
`Shared[T]` ne deviendra pas implicitement thread-safe.

## Validation et performances

Les fixtures runtime exécutées sous AddressSanitizer couvrent la création, trois
handles observant la même valeur, les libérations intermédiaires, la destruction
unique du contenu, une chaîne imbriquée de 512 nœuds et la panique du destructeur
final. Le test runtime C vérifie aussi que la saturation refuse l'incrément sans
modifier le compteur.

Le microbenchmark `benchmarks/shared_clone.janus` mesure 100 000 couples
clone/destruction avec observation. Son checksum est une porte fonctionnelle,
pas un seuil de temps CI. Les coûts attendus de `share`, `clone`, `get` et de la
libération non finale sont O(1); la dernière libération ajoute le coût du
destructeur de `T`.

## Évolutions différées

- Weak[T] et la détection assistée des cycles ;
- AtomicShared[T] pour le partage inter-thread ;
- une allocation compacte compteur + contenu ;
- une opération conditionnelle de mutation ou d'extraction lorsque le compteur
  vaut un ;
- des dérivations `Equality`, `Hashing` ou `Debug` choisissant explicitement
  identité ou contenu.
