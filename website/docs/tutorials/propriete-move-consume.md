# Maîtriser `move`, `borrow`, `consume` et la destruction

## Prérequis

- connaître classes, structs et enums ;
- avoir lu l’introduction à la [propriété](../book/05-erreurs-propriete.md).

## Résultat

Nous allons suivre une ressource depuis sa création jusqu’à sa destruction,
d’abord à travers une fonction, puis dans un agrégat, un emprunt et enfin une
méthode consommante.

## 1. Un propriétaire unique

```janus
class Token(val code : int) {
    destructor {
        println("Token détruit")
    }
}
```

Une instance de `Token` n’est pas `Copy`. La recopier créerait deux liaisons responsables d’une même allocation. Janus impose donc un transfert explicite.

```janus
// doctest: doctest name=tutorial-move-function
class Token(val code : int) {}

def useAndDestroy(token : Token) : int {
    val code : int = token.code
    delete token
    return code
}

def main() : int {
    val token : Token = new Token(42)
    val answer : int = useAndDestroy(move token)
    return answer - 42
}
```

La fonction reçoit la propriété. Après l’appel, `token` est invalidé dans `main`; la fonction le détruit exactement une fois.

## 2. Transférer dans un agrégat

```janus
// doctest: doctest name=tutorial-move-aggregate
class Token(val code : int) {}
struct Envelope(val token : Token) {}

def main() : int {
    val token : Token = new Token(42)
    val envelope : Envelope = new Envelope(move token)
    defer delete envelope
    return envelope.token.code - 42
}
```

`Envelope` devient propriétaire parce qu’il contient `Token`. Le supprimer détruit récursivement le champ. On ne peut pas déplacer `envelope.token` isolément : il faut transférer l’agrégat ou fournir une méthode qui organise correctement son extraction.

## 3. Extraire avec `consume`

```janus
// doctest: doctest name=tutorial-consume
class Token(val code : int) {}

class Envelope(val token : Token) {
    consume def takeCode() : int {
        val answer : int = token.code
        delete this
        return answer
    }

    destructor {
        delete token
    }
}

def main() : int {
    val envelope : Envelope =
        new Envelope(new Token(42))
    return envelope.takeCode() - 42
}
```

`consume def` annonce que l’appel invalide le receveur. Ici, `delete this` détruit `Token` avec l’enveloppe. Pour retourner la ressource elle-même, une implémentation doit transférer explicitement la valeur et veiller à ne pas la détruire avec son ancien conteneur ; utilisez les abstractions éprouvées de la stdlib comme modèles.

## 4. Observer avec `borrow`

Une vue n'a pas à devenir copropriétaire du stockage qu'elle consulte :

```janus
val storage : Ptr[byte] = alloc[byte](usize(16))
borrow val view : Ptr[byte] = storage
defer free(storage)
```

`view` ne peut pas être libéré, déplacé ou consommé. Sa validité dépend de
`storage`; il faut donc déclarer le propriétaire dans une portée au moins aussi
longue. Pour une relation conservée dans un objet, un champ constructeur de
classe peut s'écrire `private borrow val source : Collection`.

À la frontière C, le même mot qualifie un paramètre ou un retour `Ptr[T]` : il
décrit alors le contrat du code natif. Consultez le
[chapitre sur l'interopérabilité](../book/10-modules-visibilite-ffi.md).

## 5. Le motif `defer delete`

```janus
class Session() {}

def work() : int {
    val session : Session = new Session()
    defer delete session

    if true {
        return 42
    }
    return 0
}
```

Le nettoyage s’exécute même lors du `return`. Plusieurs `defer` sont exécutés en ordre inverse, ce qui correspond naturellement aux dépendances créées dans l’ordre.

## Lire un diagnostic de propriété

- **requires an explicit move** : une opération prend la propriété ; ajoutez `move` seulement si vous acceptez d’invalider la source.
- **used before initialization** après un transfert : la liaison a déjà été déplacée ou consommée.
- **cannot be transferred independently** : le champ appartient encore à son agrégat.
- **move requires an owning value** : le type est copiable ; `move` n’a pas de sens.
- **JANA0018** : l'alias emprunte un propriétaire temporaire qui disparaîtrait trop tôt.
- **JANA0021** : deux objets semblent se posséder cycliquement ; rendez une relation observante.

Ne corrigez pas mécaniquement tous les diagnostics en ajoutant `move`. Demandez d’abord : « qui doit détruire cette ressource après l’opération ? »

## Vérifier

```bash
janus fmt
janus check
janus test
```

## Prolongements

- Placez une ressource dans `Option[T]`, puis extrayez-la avec `match move`.
- Insérez-la dans `Array[T]` avec `push(move value)` et récupérez-la avec `remove`.
- Comparez `iterator()` et `intoIterator()` sur un tableau propriétaire.
- Consultez le [chapitre complet sur la propriété](../book/09-propriete-avancee.md).
