# Tests unitaires natifs

Une fonction publique, sans paramètre, non générique et retournant `Unit`
devient un test avec la métadonnée documentaire `@test`. Le runner génère son
point d’entrée : aucun `main` manuel n’est nécessaire.

```janus
import std.testing

/// @test
def additionWorks() : Unit {
    assertEqual[int](2 + 2, 4)
}
```

L’identifiant stable combine le chemin sous `tests/`, sans extension, et le nom
de fonction : `tests/arithmetic.janus` produit
« arithmetic.additionWorks ». Les fichiers historiques qui définissent un
`main` restent exécutables comme un test unique. La découverte et le rapport
sont triés par identifiant.

## Métadonnées et isolation

Chaque fonction s’exécute dans un processus distinct. Une panique ou un crash
n’interrompt donc pas les autres tests.

```janus
/// @test
/// @shouldPanic division by zero
def rejectsZero() : Unit {
    panic("division by zero\n")
}

/// @test
/// @ignore
def expensiveScenario() : Unit {}

/// @test
/// @serial
def exclusiveResource() : Unit {}
```

`@shouldPanic` accepte un fragment de message facultatif. `@ignore` exclut le
test par défaut ; `--ignored` n’exécute que les tests ignorés et
`--include-ignored` les réintègre. `@serial` exécute le test hors du groupe
parallèle.

## Assertions

`std.testing` fournit `fail`, `assertTrue`, `assertFalse`, `assertEqual`,
`assertNotEqual`, `assertSome`, `assertNone`, `assertOk` et `assertError`.
`assertBorrowedEqual` compare aussi les valeurs non copiables sans les
consommer. `assertSomeWhere`, `assertOkWhere` et `assertErrorWhere` appliquent
une callback `scoped` au contenu de la variante attendue ; elles permettent
donc de vérifier un objet propriétaire tout en le laissant dans son conteneur.
`assertBytesEqual` compare deux `ByteView` et signale la taille ou le premier
indice différent. Les comparaisons génériques exigent `Equality` et `Debug` ;
les valeurs sont affichées avant la panique d’assertion. Le runner rattache
chaque résultat à la déclaration source du test.

`testTemporaryDirectory(false)` retourne une ressource propriétaire enveloppée
dans un `Result`. Son chemin est unique et son destructeur supprime
récursivement l’arborescence en best-effort ; `true` la conserve intacte pour
le débogage. `TestTemporaryDirectory.cleanup()` fournit le même nettoyage sous
forme de `Result[bool, SystemError]`, observable et idempotent. Après un premier
succès, les appels suivants ne retouchent pas le chemin, ce
qui évite une double suppression dangereuse si ce nom était réutilisé.
Comme pour toute ressource propriétaire locale susceptible de traverser une
panique, utilisez
`defer delete directory` afin d’inscrire son destructeur dans le nettoyage de
panique du langage.

## CLI et rapports CI

```text
janus test --list
janus test addition
janus test --exact arithmetic.additionWorks
janus test --ignored
janus test --include-ignored
janus test --jobs 8
janus test --timeout 30s
janus test --fail-fast
janus test --fail-if-empty
janus test --format human
janus test --format json
janus test --format junit
```

La sortie reste ordonnée même avec plusieurs workers. stdout et stderr sont
capturés et montrés pour les échecs ; JSON et JUnit les incluent toujours. Le
JSON porte `schema_version: 1` et représente l’identifiant, le type de test,
la source, la position, le statut, la durée, le message et les flux capturés.
Les résumés comptent séparément tests unitaires et doctests.

Codes de sortie : `0` si tout réussit, `1` pour un échec de test, une panique
inattendue, un crash, un timeout ou une erreur de compilation/découverte, `2`
pour une invocation invalide, et `4` lorsqu’aucun test n’est découvert avec
`--fail-if-empty`.

La [matrice de validation des emprunts](borrow-validation.md) décrit les tests
de langage, diagnostics, sanitizers et canaris aval qui protègent les garanties
d'aliasing et de durée de vie.
