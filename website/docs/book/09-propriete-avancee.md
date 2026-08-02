<span class="chapter-kicker">CHAPITRE 09 / POSSÉDER ET TRANSFÉRER</span>
# Propriété avancée

## Objectifs

- reconnaître une valeur propriétaire ;
- transférer une ressource avec `move` ;
- créer un alias observant avec `borrow` ;
- écrire une méthode `consume` ;
- organiser le nettoyage avec `delete`, `defer` et `destructor`.

## Valeur copiable ou propriétaire

Un `int` et un type qui dérive `Copy` peuvent être réutilisés après une affectation. Une instance de classe, une closure et tout agrégat qui contient une ressource ont au contraire un propriétaire unique.

```janus
class FileToken(val identifier : int) {
    destructor {
        println(identifier)
    }
}
```

`new FileToken(42)` crée une ressource. Elle doit être détruite exactement une fois ou transférée à une autre valeur qui en devient responsable.

## `move` rend le transfert visible

```janus
// doctest: doctest name=explicit-move
class Resource(val identifier : int) {}

def dispose(resource : Resource) : Unit {
    delete resource
}

def main() : int {
    val resource : Resource = new Resource(42)
    dispose(move resource)
    return 0
}
```

`move resource` transfère la propriété et invalide la liaison source. Lire, déplacer ou supprimer ensuite `resource` est une erreur de compilation. `move` n’est admis que sur un identifiant local propriétaire : il ne sert ni à copier un entier ni à déplacer directement un champ hors de son parent.

Les paramètres de fonctions propriétaires sont consommés. L’appel doit donc écrire `move` quand il passe une liaison locale. Le retour d’une ressource exige lui aussi `return move value`.

## `borrow` observe sans devenir propriétaire

`borrow val` crée une liaison immutable qui observe un propriétaire sans
recevoir la responsabilité de le détruire. L'alias ne peut être ni déplacé,
ni passé à `delete` ou `free`, et ne doit jamais survivre à sa source :

```janus
val storage : Ptr[byte] = alloc[byte](usize(16))
borrow val view : Ptr[byte] = storage
defer free(storage)
```

Une classe peut conserver une relation observante avec un champ constructeur
`borrow val`. Ce champ n'est pas détruit avec la classe et permet notamment de
casser un cycle de propriété. La source doit vivre plus longtemps que l'objet
observateur. Les structs ne peuvent pas contenir de champ emprunté.

Le diagnostic `JANA0018` signale un emprunt créé depuis un propriétaire
temporaire; `JANA0021` signale un cycle potentiel entre propriétaires.

## Agrégats propriétaires

Une structure ou un enum devient propriétaire dès qu’un champ ou payload l’est :

```janus
// doctest: doctest name=owning-aggregate
class Resource(val identifier : int) {}
struct Box(val resource : Resource) {}
enum Slot { Full(Box), Empty }

def main() : int {
    val box : Box = new Box(new Resource(42))
    val slot : Slot = Slot.Full(move box)
    val extracted : Box = match move slot {
        Full(value) => move value,
        Empty => new Box(new Resource(0))
    }
    defer delete extracted
    return extracted.resource.identifier - 42
}
```

Le `match move slot` consomme l’enveloppe et transfère uniquement le payload actif. `delete extracted` détruit récursivement la structure, puis sa ressource.

## `consume` sur une méthode

Une méthode ordinaire emprunte son receveur : l’objet reste utilisable après l’appel. Une méthode préfixée par `consume` prend possession de `this` et doit terminer son cycle de vie :

```janus
// doctest: doctest name=consume-method
class Box(val value : int) {
    consume def take() : int {
        val result : int = value
        delete this
        return result
    }
}

def main() : int {
    val box : Box = new Box(42)
    val answer : int = box.take()
    return answer - 42
}
```

Après `box.take()`, `box` est invalidé. Le mot-clé appartient au contrat : une méthode qui implémente une méthode `consume` d’un trait doit également être `consume`.

Une consommation conditionnelle ou répétée est volontairement stricte. Ne consommez pas la même liaison depuis une boucle ou une closure ; organisez le contrôle de flux pour que le transfert n’ait qu’un chemin certain.

## `delete`, `defer` et `destructor`

- `delete value` termine immédiatement la ressource et invalide la liaison.
- `defer expression` enregistre une action pour la sortie de la portée, y compris par `return`, `break` ou `continue`.
- `destructor { ... }` définit le nettoyage interne d’une classe ; il s’exécute avant la libération de l’objet.

```janus
class Buffer(val identifier : int) {
    destructor {
        println("fermeture")
    }
}

def work() : int {
    val buffer : Buffer = new Buffer(1)
    defer delete buffer
    return 42
}
```

Les `defer` d’une portée s’exécutent dans l’ordre inverse de leur déclaration. Déclarez d’abord une ressource, puis immédiatement son nettoyage : ce motif rend les retours anticipés sûrs et lisibles.

## Ressources globales et collections

Une globale propriétaire est obligatoirement un `val`. Le runtime la détruit automatiquement après `main`, dans l’ordre inverse de l’initialisation ; elle ne peut pas être déplacée ou supprimée manuellement.

Les collections possèdent les éléments qu’on leur transfère. `push(move value)` remet la responsabilité au tableau ; `remove` la rend à l’appelant ; `clear` et le destructeur éliminent les éléments restants. `intoIterator()` consomme la collection alors que `iterator()` copie ses éléments et exige donc `Copy`.

## Exercice

Créez une classe `Ticket` dont la méthode `consume def redeem() : int` retourne son code, détruit `this`, puis utilisez-la une seule fois.

??? success "Correction"
    ```janus
    class Ticket(val code : int) {
        consume def redeem() : int {
            val result : int = code
            delete this
            return result
        }
    }

    def main() : int {
        val ticket : Ticket = new Ticket(42)
        return ticket.redeem() - 42
    }
    ```

<div class="lesson-nav"><a href="../08-traits-derivations/">← Traits et dérivations</a><a href="../10-modules-visibilite-ffi/">Modules, visibilité et C →</a></div>
