<span class="chapter-kicker">CHAPITRE 11 / UTILISER LA STDLIB</span>
# Bibliothèque standard

## Objectifs

- choisir le bon module sans réinventer un service ;
- convertir du texte et traiter les erreurs ;
- manipuler chemins, fichiers, flux et processus ;
- travailler avec le temps, les mathématiques et le hasard.

## Carte des modules

| Besoin | Modules principaux |
| --- | --- |
| collections | `std.array`, `std.hashset`, `std.hashmap`, `std.range` |
| itération/construction | `std.iterator`, `std.builder`, `std.array_builder` |
| absence/erreur | `std.option`, `std.result` |
| texte/conversion | `std.text` |
| chemins/fichiers | `std.path`, `std.fs`, `std.io`, `std.system` |
| programme/processus | `std.process` |
| calcul/hasard | `std.math`, `std.random` |
| temps | `std.time`, `std.wall_time` |
| interopérabilité | `std.c` |
| graphisme | modules `std.graphics.*` du chapitre suivant |

La [référence API générée](../reference/stdlib/index.html) est la source exacte des signatures. Ce chapitre explique comment choisir les abstractions.

## Texte et conversion

Les chaînes `string` sont UTF-8 et immuables. `std.text` fournit un `TextBuilder` possédé pour assembler progressivement du texte, des fonctions de formatage (`intText`, `doubleText`, formats hexadécimaux ou fixes) et des parseurs qui retournent `Result`.

```janus
import std.text

val parsed : Result[int, ParseError] = parseInt("42")
```

Une conversion susceptible d’échouer ne retourne pas une valeur sentinelle : traitez `ParseError` ou propagez-la avec `?`. Pour construire une longue chaîne, préférez un builder à une succession d’allocations intermédiaires.

## Chemins, fichiers et flux

`std.path` normalise et combine des chemins de manière portable. `std.fs` couvre les opérations de haut niveau : lecture/écriture de fichier, métadonnées, répertoires et dossiers temporaires. `std.io` fournit des lecteurs/écrivains tamponnés, des buffers d’octets et les flux standards.

```janus
import std.fs

val content : Result[FileData, SystemError] =
    readFile("notes.txt")
```

Ces API retournent des types propriétaires quand elles allouent une ressource. Examinez leur signature, transférez avec `move` lorsque demandé et placez le `defer delete` immédiatement après une extraction réussie.

`std.system` expose une couche plus basse autour des handles et erreurs système. Utilisez-la pour un besoin que `std.fs` ou `std.io` ne couvre pas, en conservant le contexte d’erreur.

## Arguments, environnement et processus

`std.process` permet de lire les arguments du programme, consulter une variable d’environnement et exécuter un processus enfant. Le résultat d’un processus regroupe son code de sortie, sa sortie standard et sa sortie d’erreur.

Traitez trois niveaux d’échec séparément :

1. l’appel système peut ne pas démarrer le processus ;
2. le processus peut terminer avec un code non nul ;
3. son texte peut être invalide pour l’usage attendu.

Cette séparation produit de meilleurs diagnostics qu’un simple booléen.

## Mathématiques

`std.math` fournit constantes, valeurs absolues, minimum/maximum, arrondis, puissances, racines, logarithmes, trigonométrie, interpolation et helpers sur les flottants. Les variantes `float` et `double` sont distinctes : évitez les conversions implicites qui n’existent pas.

## Temps monotone et temps civil

Utilisez `std.time` pour mesurer une durée : `Instant`, `Duration` et `monotonicNow()`. Une horloge monotone ne recule pas lorsque l’heure système change.

Utilisez `std.wall_time` pour une date civile, un timestamp Unix, UTC ou l’heure locale. Ne mesurez pas des performances avec une horloge civile.

```janus
import std.time

val start : Instant = monotonicNow()
// travail
val elapsed : Duration = start.elapsed()
println(elapsed.milliseconds())
```

## Nombres pseudo-aléatoires

`std.random` fournit un générateur déterministe et un amorçage système. Injectez un générateur initialisé avec une graine fixe dans les tests ; utilisez l’amorçage système lorsque la reproductibilité n’est pas requise. Un PRNG ordinaire ne constitue pas une source cryptographique.

## Choisir une abstraction

- Commencez par le module de plus haut niveau qui exprime votre intention.
- Regardez si la valeur de retour est `Option`, `Result` ou une classe propriétaire.
- Consultez les contraintes génériques : `Copy` signale souvent une copie, `consume` un transfert.
- Utilisez les noms qualifiés (`std.result.map`) si plusieurs modules exportent un même nom.

## Exercice

Expliquez quels modules choisir pour : mesurer le temps d’une lecture de fichier, convertir son contenu en entier et produire un hasard reproductible dans un test.

??? success "Correction"
    - `std.time` pour `Instant` et la durée monotone ;
    - `std.fs` pour lire le fichier ;
    - `std.text` pour parser l’entier en `Result` ;
    - `std.random` avec une graine fixe pour le test.

<div class="lesson-nav"><a href="../10-modules-visibilite-ffi/">← Modules, visibilité et C</a><a href="../12-graphisme-audio/">Graphisme 2D et audio →</a></div>
