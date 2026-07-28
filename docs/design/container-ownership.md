# Décision d'architecture : propriété des conteneurs

Statut : acceptée pour Janus 0.6.0, étendue aux itérateurs en 0.6.1.

Cette décision fixe le contrat cible des conteneurs qui stockent des valeurs
propriétaires, avant son implémentation dans `Array`. Elle complète le
[contrat de stabilité](../stability-contract.md#propriété-déplacement-et-destruction).
Le format exact des API reste expérimental avant 1.0, mais les garanties de
propriété ci-dessous sont normatives pour les travaux 0.6.x.

## Problème

En 0.5.x, `Array[T]` impose `T <: Copy`. Retirer cette contrainte sans définir
qui possède chaque élément permettrait de copier implicitement une ressource,
de la détruire deux fois ou de conserver une référence invalidée après une
réallocation.

Une valeur `T` est dite propriétaire lorsqu'elle ne satisfait pas `Copy` :
classe, closure avec environnement, struct ou enum contenant récursivement une
de ces valeurs. Un conteneur propriétaire possède chaque emplacement initialisé
de son stockage. Un déplacement transfère cette propriété ; une observation ne
la transfère jamais.

## Options étudiées

| Option | Avantages | Limites | Décision |
| --- | --- | --- | --- |
| emprunt général avec durée de vie | modèle uniforme et expressif | impose références, annotations de durée de vie et analyse interprocédurale | différé après 0.6.x |
| callback d'observation borné | durée de vie délimitée par un appel, pas d'alias persistant | callback propriétaire soumis à des règles contextuelles | retenu pour 0.6.x |
| vue persistante sur le stockage | parcours efficace et API familière | invalidée par réallocation, mutation ou destruction ; nécessite des durées de vie | rejeté pour les valeurs propriétaires |
| opérations uniquement consommantes | implémentation simple et aucune référence pendante | impossible d'inspecter un élément sans le retirer | retenu pour les transferts, insuffisant seul |

Janus 0.6.x combine donc callbacks bornés et opérations consommantes. Il
n'introduit ni type `Ref[T]`, ni tranche propriétaire, ni vue persistante.

## Invariants

Pour un `Array[T]` de longueur `n` :

1. les emplacements `[0, n)` sont initialisés et possédés exclusivement par le
   tableau ;
2. les emplacements `[n, capacity)` ne possèdent aucune valeur vivante ;
3. une valeur propriétaire entre ou sort du tableau par un déplacement
   explicite, jamais par copie ;
4. chaque valeur entrée est soit ressortie vers l'appelant, soit détruite
   exactement une fois par une opération du tableau ;
5. une observation ne survit jamais au callback qui l'a reçue ;
6. une mutation, une réallocation ou la destruction du tableau est interdite
   pendant une observation.

Une classe, un struct propriétaire et un enum propriétaire suivent exactement
les mêmes règles. Le conteneur ne distingue pas leur représentation.

## Contrat des opérations

Les signatures ci-dessous décrivent la surface cible. La disponibilité
« `Copy` » signifie que l'opération est rejetée lorsque `T` est propriétaire.
Pour un type `Copy`, les signatures sûres de 0.5.x conservent leur sens.

| Opération | `T: Copy` | `T` propriétaire | Effet de propriété |
| --- | --- | --- | --- |
| `push(value)` | copie `value` | exige `move value` | transfère le nouvel élément au tableau |
| `get(index)` | retourne une copie | indisponible | observe sans modifier le tableau |
| `withValue(index, callback)` | autorisée | autorisée avec callback littéral borné | observe ; aucun transfert |
| `set(index, value)` | copie et remplace | exige `move value` | installe la nouvelle valeur, puis détruit l'ancienne |
| `replace(index, value)` | copie | exige `move value` | installe la nouvelle valeur et déplace l'ancienne vers l'appelant |
| `remove(index)` | retourne une copie retirée | déplace la valeur retirée vers l'appelant | le tableau ne détruit pas la valeur retournée |
| `pop()` | retourne une copie retirée | déplace le dernier élément vers l'appelant | identique à `remove` sans décalage |
| `clear()` | oublie les valeurs copiables | détruit tous les éléments | conserve l'allocation, longueur finale nulle |
| `foreach(callback)` | passe des copies | emprunts bornés successifs | observe sans transférer |
| `iterator()` | conserve le contrat 0.5.x | indisponible | produit des copies |
| `intoIterator()` | consomme le tableau | consomme le tableau | déplace chaque élément ; détruit les éléments non visités |
| `delete array` | libère le stockage | détruit les éléments puis libère le stockage | invalide le tableau |

`set` est l'opération volontairement destructive. Elle déplace d'abord
l'ancienne valeur dans un temporaire de nettoyage, installe la nouvelle, puis
détruit le temporaire. Le tableau reste donc valide si ce destructeur panique.
`replace` doit être utilisée quand l'appelant veut récupérer l'ancienne valeur.
Une valeur retournée par
`remove`, `pop`, `replace` ou `intoIterator` devient la responsabilité de
l'appelant.

### Observation par callback

Pour une valeur propriétaire, `withValue` et `foreach` n'acceptent en 0.6.0
qu'une lambda littérale dont le paramètre d'élément est traité comme un emprunt
contextuel. La syntaxe reste celle d'une lambda existante :

```janus
resources.withValue(usize(0), (resource : Resource) => resource.inspect())
```

Dans ce corps, le paramètre peut appeler des méthodes non `consume` et lire des
champs. Il ne peut pas être déplacé, supprimé, retourné, stocké dans un
agrégat propriétaire ni capturé par une closure qui s'échappe. Le callback
retourne `Unit`. Ces restrictions évitent d'ajouter un type de référence
public avant qu'un modèle général d'emprunt soit conçu.

Le tableau lui-même est considéré observé pendant l'appel. Le callback ne peut
donc pas appeler `push`, `set`, `replace`, `remove`, `clear`, `reserve`,
`intoIterator` ou `delete` sur ce tableau, directement ou par capture.

## Invalidation et aliasing

- L'emprunt reçu par un callback expire à son retour, même si le stockage n'a
  pas bougé.
- `push`, `reserve` et toute croissance peuvent réallouer et invalident toutes
  les adresses internes.
- `set` et `replace` invalident l'ancienne valeur à l'index concerné.
- `remove` invalide l'index retiré et peut déplacer tous les éléments suivants.
- `clear`, `intoIterator` et `delete` invalident tous les éléments.
- Aucun pointeur ou `string` dérivé d'un élément ne peut être conservé au-delà
  du callback sauf si son propre contrat garantit une durée de vie indépendante
  du conteneur.

Les observations imbriquées en lecture sont permises seulement si elles ne
portent pas sur un tableau capturé de façon mutable. Il n'existe pas d'emprunt
mutable d'un élément en 0.6.0 ; une modification remplace explicitement la
valeur avec `set` ou `replace`.

## Réallocation

Une croissance alloue d'abord le nouveau bloc. En cas d'échec, le tableau
original reste inchangé. Après succès, chaque emplacement initialisé est
transféré vers le nouveau bloc sans exécuter son destructeur ; l'ancien bloc
brut est ensuite libéré. Le transfert de stockage n'est pas une copie de `T`
et ne crée jamais deux propriétaires logiques.

Pour `push(move value)`, la fonction appelée possède `value` dès l'entrée. Si la
réallocation échoue et panique, cette valeur entrante est détruite une fois
avant la terminaison, tandis que les éléments déjà présents restent la
responsabilité du tableau et sont nettoyés avec lui.

## Panique et sorties anticipées

Les contrôles d'index ont lieu avant toute modification d'un emplacement.
Après le transfert d'un argument propriétaire, l'opération garde un nettoyage
armé jusqu'à ce que la nouvelle valeur soit installée. Une panique ne peut donc
ni perdre l'argument ni détruire deux fois l'ancienne valeur.

`clear` et le destructeur traitent les éléments depuis la fin : ils décrémentent
d'abord la longueur, puis détruisent l'ancien dernier élément. Si un destructeur
panique, le préfixe `[0, length)` reste valide et aucun nettoyage ultérieur ne
doit tenter de détruire de nouveau l'emplacement déjà retiré.

Un `return`, `break`, `continue`, `?` ou une panique termine l'observation
avant les nettoyages de portée. Un `intoIterator` détruit dans son propre
destructeur le conteneur consommé et tous les éléments qui n'ont pas encore été
produits. Les adaptateurs propriétaires conservent cette responsabilité :
`filter` détruit les éléments refusés, `take` détruit la partie non visitée et
la destruction de tout pipeline remonte jusqu'à la source.

## Diagnostics attendus

Le compilateur doit diagnostiquer au point d'utilisation :

- un argument propriétaire passé à une opération de transfert sans `move` ;
- toute utilisation d'une valeur après son déplacement dans le tableau ;
- `get` ou `iterator` avec un type ne satisfaisant pas `Copy`, avec suggestion
  de `withValue`/`foreach` ou `remove`/`intoIterator` selon l'intention ;
- le déplacement, `delete`, retour, stockage ou capture d'un paramètre emprunté ;
- une mutation ou consommation du tableau pendant un callback d'observation ;
- toute tentative de conserver une observation après le retour du callback.

Le texte exact reste non stable, mais le diagnostic doit identifier la valeur,
le premier transfert ou emprunt pertinent et l'opération incompatible.

## Exemples de transfert

Classe :

```janus
val resource : Resource = new Resource(1)
resources.push(move resource)
resources.withValue(usize(0), (item : Resource) => item.inspect())
val recovered : Resource = resources.remove(usize(0))
defer delete recovered
```

Struct propriétaire :

```janus
struct Box(val resource : Resource) {}

val box : Box = new Box(new Resource(2))
boxes.push(move box)
val newBox : Box = new Box(new Resource(3))
val replaced : Box =
    boxes.replace(usize(0), move newBox)
defer delete replaced
```

Enum propriétaire :

```janus
enum Slot { Occupied(Box), Empty }

val slot : Slot = Slot.Occupied(move box)
slots.push(move slot)
slots.foreach((item : Slot) => inspectSlot(item))
```

Les fixtures compilables
[`container_owned_class.janus`](../../tests/fixtures/design/container_owned_class.janus),
[`container_owned_struct.janus`](../../tests/fixtures/design/container_owned_struct.janus)
et
[`container_owned_enum.janus`](../../tests/fixtures/design/container_owned_enum.janus)
verrouillent les mécanismes de classe, d'agrégat et de déplacement sur lesquels
ce contrat repose. L'API `Array` cible sera couverte directement lors de son
implémentation.

## Conséquences

- `Array[T]` retire sa contrainte globale `T <: Copy`.
- La compatibilité 0.5.x est conservée pour les opérations `Copy`.
- Les API d'observation et de consommation deviennent distinctes.
- Les collections et itérateurs 0.6.1 reprennent ces mêmes invariants.
- Un futur système général de références pourra remplacer l'emprunt contextuel
  sans autoriser davantage de copies implicites.
