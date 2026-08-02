<span class="chapter-kicker">CHAPITRE 10 / ORGANISER ET INTEROPÉRER</span>
# Modules, visibilité et interopérabilité C

## Objectifs

- nommer et importer un module ;
- choisir entre surface publique, `private` et `internal` ;
- qualifier un symbole ambigu ;
- déclarer les contrats de propriété d’une frontière `extern`.

## Déclarer et importer

Un fichier peut commencer par une déclaration `module`, suivie de ses imports :

```janus
module app.geometry

import std.math
import std.option
```

Le point-virgule est facultatif après `module` et `import`. Toutes les autres déclarations suivent. Un symbole public importé peut être utilisé directement ou avec son nom qualifié, par exemple `std.option.map`.

La qualification est utile lorsque deux modules exportent le même nom. Deux exports publics ambigus provoquent un diagnostic au lieu d’un choix silencieux.

## Trois niveaux de visibilité

Sans modificateur, une déclaration de premier niveau est publique. `private` la limite au module :

```janus
module app.settings

private val secret : int = 42

private def readSecret() : int {
    return secret
}

def publicValue() : int {
    return readSecret()
}
```

Dans une classe :

- un membre public est accessible partout où le type l’est ;
- un membre `private` est accessible seulement depuis la classe ;
- un membre `internal` est accessible aux autres déclarations du même module, mais pas aux modules importateurs.

```janus
class Account(
    private val passwordHash : usize,
    internal val identifier : int,
    val displayName : string
) {}
```

`internal` ne modifie que les champs et méthodes de classe. Il n’est pas valide au niveau supérieur.

## Documentation publique

Les commentaires `///` placés juste avant une déclaration alimentent `janus doc` :

```janus
/// Calcule la somme de deux entiers.
/// @param left Premier opérande.
/// @param right Second opérande.
/// @return Somme des opérandes.
def add(left : int, right : int) : int {
    return left + right
}
```

Documentez chaque paramètre public avec `@param` et chaque retour non `Unit` avec `@return`. Les commentaires `//` ordinaires restent internes au code.

## `extern` : déclarer une fonction native

`extern def` annonce qu’une fonction est fournie par une bibliothèque native liée au programme :

```janus
import std.c

extern("native_add") def add(left : int, right : int) : int
```

Une fonction externe n’a pas de corps Janus. `extern("symbole")` permet de
choisir un nom Janus différent du symbole C. La convention d’appel et les
types doivent correspondre exactement à l’ABI C. Les fonctions variadiques
utilisent `...`, comme `printf(format, ...)` dans `std.c`.

Les paramètres pointeur doivent annoncer ce que fait le code natif :

```janus
extern def inspect(borrow data : Ptr[byte]) : Unit
extern def release(consume data : Ptr[byte]) : Unit
extern def lastError() : borrow Ptr[byte]
extern def allocateBuffer() : owned Ptr[byte]
```

- `borrow` sur un paramètre garantit que le natif ne conserve ni ne libère le pointeur ;
- `consume` transfère le pointeur au natif et invalide la liaison appelante ;
- un retour `borrow` reste détenu par le code natif ;
- un retour `owned` devient une ressource que Janus doit libérer ou transférer.

`borrow` est un mot-clé réservé. `owned` est un qualificateur contextuel : il
n'est reconnu à cet emplacement que sur le retour pointeur d'une déclaration
externe. Les diagnostics `JANA0020` et `JANA0022` signalent respectivement un
paramètre ou un retour pointeur externe sans contrat.

!!! danger "Frontière non sûre"
    Les qualificateurs rendent le transfert visible, mais le compilateur ne
    peut toujours pas vérifier la taille ou la validité des données manipulées
    par le code C. Encapsulez les `extern def` dans un module étroit et exposez
    une API Janus typée et propriétaire.

## Pointeurs et mémoire brute

`Ptr[T]` est le type de pointeur bas niveau. Les builtins `alloc[T](count)` et `free(pointer)` gèrent un bloc brut ; `load`, `store` et l’arithmétique de pointeur donnent accès à ses éléments selon la surface documentée.

```janus
val data : Ptr[int] = alloc[int](usize(4))
defer free(data)
data.store(usize(0), 42)
val answer : int = data.load(usize(0))
```

Préférez `Array`, `ByteBuffer`, `Path` et les autres enveloppes de la stdlib. Les pointeurs sont nécessaires pour l’interopérabilité et certaines structures internes, pas pour le code applicatif courant.

## Exercice

Écrivez un module `app.answer` qui garde une constante privée et exporte une fonction documentée `answer() : int`.

??? success "Correction"
    ```janus
    module app.answer

    private val storedAnswer : int = 42

    /// Retourne la réponse configurée.
    /// @return Valeur publique du module.
    def answer() : int {
        return storedAnswer
    }
    ```

<div class="lesson-nav"><a href="../09-propriete-avancee/">← Propriété avancée</a><a href="../11-bibliotheque-standard/">Bibliothèque standard →</a></div>
