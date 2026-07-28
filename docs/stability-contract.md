# Contrat de stabilité Janus 1.0

Statut : proposition normative pour Janus 1.0.

Ce document définit les garanties publiques que Janus prendra à partir de la
version 1.0. Il ne transforme pas rétroactivement les versions 0.x en versions
stables : avant 1.0, toute surface peut encore changer, mais une modification
doit être annoncée dans le changelog.

Les mots « doit », « ne doit pas » et « garanti » désignent une obligation de
compatibilité. Les guides décrivent l'usage courant ; en cas de contradiction
à partir de Janus 1.0, ce contrat prévaut.

## Périmètre public

Le contrat couvre :

- les sources Janus valides et leur comportement observable ;
- le modèle de propriété et le nettoyage des ressources ;
- l'interface C acceptée par `extern def` ;
- les manifestes `janus.toml`, les lockfiles `janus.lock` et la résolution ;
- les modules de bibliothèque standard déclarés stables ;
- les commandes documentées de `janus` et `janusup` ;
- les archives officielles sur les plateformes prises en charge.

Ne font pas partie du contrat :

- les classes C++, l'AST, l'IR LLVM et les symboles internes du runtime ;
- le texte exact, l'ordre ou la ponctuation des diagnostics ;
- la forme de l'IR et du code machine, les optimisations et les performances ;
- le contenu des répertoires de cache et des fichiers temporaires ;
- les API marquées expérimentales ci-dessous.

## Versionnage et compatibilité

À partir de 1.0, Janus suit le versionnage sémantique :

- une version corrective peut corriger un défaut sans modifier une API
  publique valide ;
- une version mineure peut ajouter de la syntaxe, des API ou des diagnostics,
  mais doit accepter les programmes valides de la version mineure précédente ;
- une version majeure peut retirer ou modifier une garantie après la procédure
  de dépréciation et de migration ;
- une correction de sécurité ou d'indéfinition peut être incompatible si
  conserver le comportement met les utilisateurs en danger. Elle doit être
  signalée explicitement.

La compatibilité source signifie qu'un projet valide avec Janus N doit être
recompilable avec N+1 sans modification lorsque toutes les API utilisées sont
stables. La sortie et les effets observables couverts par ce document doivent
rester équivalents.

Les exécutables Janus sont autonomes. Un exécutable construit avec N doit
continuer à s'exécuter sur une plateforme officiellement prise en charge par
N+1, sous réserve des garanties du système d'exploitation. Janus ne promet ni
compatibilité des fichiers objets entre plateformes, ni chargement dynamique
d'une bibliothèque standard d'une autre version.

## Syntaxe et sémantique garanties

La grammaire et les comportements présentés dans le
[guide du langage](language-guide.md) deviennent stables en 1.0, notamment :

- déclarations `val` et `var`, fonctions, classes, structs, enums et traits ;
- génériques, contrainte intrinsèque `Copy` et fonctions de première classe ;
- demandes structurelles explicites avec la clause fermée `derives` ;
- modules, imports et visibilités `private` et `internal` ;
- `if`, `else if`, `while`, `for`, `break`, `continue` et `match` ;
- `Option`, `Result`, l'opérateur `?`, `defer`, `move` et `delete` ;
- pointeurs typés, casts explicites et fonctions externes.

Une version mineure peut rendre valide un ancien programme rejeté. Elle ne doit
pas changer le sens d'un programme déjà valide. Les nouveaux mots-clés doivent
être introduits de manière à produire un diagnostic et une migration claire en
cas de collision avec un identifiant existant.

Les noms publics d'un module, les signatures et les règles de visibilité font
partie de la compatibilité source. Les noms privés et l'organisation interne
d'un module peuvent changer.

## Propriété, déplacement et destruction

Une classe, une closure avec environnement et tout agrégat qui en contient
possèdent une ressource. Les garanties suivantes sont stables :

- `move value` transfère la propriété et toute réutilisation de la source est
  rejetée à la compilation ;
- `delete value` détruit exactement une fois la ressource possédée et ses
  champs possédés, puis invalide la valeur ;
- `defer operation` exécute l'opération en sortie de sa portée, en ordre LIFO,
  y compris lors d'un `return`, `break`, `continue` ou d'une panique ;
- le destructeur d'une classe est exécuté avant la libération de son stockage ;
- structs et enums détruisent récursivement uniquement leur contenu actif ;
- les globales possédées initialisées sont détruites en ordre inverse après
  `main` et lors d'une panique ; une globale dont l'initialisation a échoué
  n'est pas détruite ;
- les types satisfaisant `Copy` sont copiables et ne transfèrent pas de
  propriété.

Les adresses d'objets, l'ordre des allocations et l'algorithme de l'allocateur
ne sont pas garantis. Le programme reste responsable d'associer toute
allocation manuelle à `delete`, `free` ou un `defer` adapté.

Une panique arrête le chemin d'exécution normal après les nettoyages garantis.
Le format complet du message, le code de sortie exact et la présence d'une
trace ne sont pas des interfaces stables.

## Entiers, overflow et conversions

Les tailles publiques sont :

| Janus | Largeur | C avec `<stdint.h>` |
| --- | ---: | --- |
| `byte`, `ubyte` | 8 bits | `int8_t`, `uint8_t` |
| `short`, `ushort` | 16 bits | `int16_t`, `uint16_t` |
| `int`, `uint` | 32 bits | `int32_t`, `uint32_t` |
| `long`, `ulong` | 64 bits | `int64_t`, `uint64_t` |
| `char` | 32 bits | `uint32_t` |
| `float`, `double` | 32 et 64 bits IEEE 754 | `float`, `double` |
| `isize`, `usize` | largeur du pointeur | `intptr_t`, `size_t` |

Pour les entiers, `+`, `-`, `*` et le moins unaire s'enroulent modulo la
largeur du type. `/` et `%` paniquent pour un diviseur nul ; les opérations
signées paniquent aussi pour `MIN / -1` et `MIN % -1`.

Un rétrécissement entier conserve les bits de poids faible. Un élargissement
étend le signe d'un type signé et complète avec des zéros un type non signé.
Les conversions implicites entre types numériques restent interdites.

Une conversion flottant-vers-entier tronque vers zéro. Seules les valeurs
finies et représentables après troncature ont un résultat garanti ; utiliser
une autre valeur est hors contrat jusqu'à ce qu'une API vérifiée soit définie.

L'évaluation constante doit produire le même résultat observable que
l'évaluation à l'exécution ou rejeter le programme lorsqu'elle détecte une
expression invalide.

## ABI C

`extern def` utilise l'ABI C native de la plateforme. Les types scalaires du
tableau précédent, `bool`, `Ptr[T]` et `Unit` sont autorisés. `bool` correspond
à `_Bool`/`bool`, un pointeur à `T *` et `Unit` à `void`.

Les chaînes, classes, structs, enums et fonctions Janus ne sont pas exposables
directement par valeur. Une chaîne est transmise explicitement avec `cstr()` et
un pointeur ; sa représentation interne n'est pas une ABI publique. Les
arguments variadiques suivent les promotions par défaut du C.

Le nom fourni par `extern("symbol")` est le nom de liaison. La disposition des
agrégats Janus, le nom des fonctions Janus non exportées et les symboles
`janus_*` du runtime restent internes.

Une ABI native n'est garantie qu'entre artefacts construits pour la même
plateforme, la même architecture et un ABI système compatible.

## Manifestes, lockfiles et dépendances

Le format stable de `janus.toml` comprend :

- `[package]` avec `name`, `version` et `entry` ;
- `[dependencies]` avec sources locales, Git ou registre et contraintes de
  version documentées ;
- des versions conformes au versionnage sémantique.

Une version mineure peut ajouter un champ facultatif. Un champ stable existant
ne change pas de sens avant une version majeure.

`janus.lock` est généré, porte un numéro de format et enregistre pour chaque
dépendance son nom, sa source, sa version et, pour Git, sa révision. Une version
1.x doit lire tous les lockfiles de format `version = 1`. Un nouveau format
doit utiliser un nouveau numéro, fournir une migration et ne jamais réécrire
silencieusement un lockfile en mode `--locked`.

`--locked` échoue si le lockfile manque ou ne correspond plus au manifeste.
`--offline` n'accède pas au réseau et échoue lorsqu'une dépendance nécessaire
n'est pas disponible localement. Deux dépendances du même nom mais de sources
différentes restent un conflit.

Le protocole du registre, son hébergement et l'organisation des caches locaux
ne sont pas stables.

## Bibliothèque standard

À la publication de 1.0, la liste exacte des modules stables doit être figée
dans cette section et dans le changelog. La proposition initiale couvre les API
publiques documentées de :

- `std.array`, `std.array_builder`, `std.iterator`, `std.option` et
  `std.result` ;
- `std.hashing`, `std.hashset`, `std.hashmap`, `std.system` et `std.builder` ;
- `std.math`, `std.text`, `std.time`, `std.wall_time` et `std.random` ;
- `std.c` pour les primitives C documentées.

Dans une version 1.x, une signature stable ne peut pas être retirée. Une
nouvelle surcharge ou méthode peut être ajoutée si elle ne rend pas un ancien
programme ambigu. Les types d'erreur et effets documentés font partie du
contrat ; le texte exact d'une panique et les algorithmes internes n'en font
pas partie.

La même graine de `Random` doit produire une suite déterministe dans une
version donnée, mais la suite numérique exacte n'est pas garantie entre
versions mineures. Les programmes qui persistent une séquence doivent
enregistrer la version ou utiliser leur propre algorithme.

## API expérimentales

Les surfaces suivantes restent explicitement hors du contrat 1.0 tant qu'une
release ne les promeut pas :

| Surface | Partie expérimentale | Condition de promotion |
| --- | --- | --- |
| `std.graphics` | backend dynamique, ressources GPU/audio, valeurs et modes ajoutés après 0.6 | tests sur les trois plateformes et contrat de portée/erreur complet |
| Publication au registre | transport, authentification, stockage et cache | protocole versionné et registre de référence |
| LSP et extension VS Code | extensions de protocole, indexation et réglages éditeur | matrice de compatibilité toolchain/extension publiée |
| Diagnostics | texte, codes et ordre des notes | schéma de codes de diagnostic versionné |
| IR et objets Janus | noms, disposition des agrégats et métadonnées LLVM | ABI de module séparée et versionnée |

Une API expérimentale doit être identifiée dans sa documentation. Son
utilisation n'oblige pas une version mineure à préserver la compatibilité.
Cette table est revue à chaque release ; retirer une ligne signifie soit une
promotion documentée avec des tests, soit la suppression de la surface avant
1.0.

## Plateformes officiellement supportées

Les plateformes de niveau 1 proposées pour 1.0 sont :

| Système | Architecture | Garantie |
| --- | --- | --- |
| Linux | x86_64 | compilation, tests et archive officielle |
| macOS | Apple Silicon ARM64 | compilation, tests et archive officielle |
| Windows | x86_64 | compilation, tests et archive officielle |

Une plateforme de niveau 1 doit réussir la CI, le smoke test de l'archive et la
suite de compatibilité avant une release. Les autres plateformes sont
communautaires et sans garantie de correctif.

Le support porte sur les versions de systèmes encore maintenues annoncées dans
les notes de release. Retirer une architecture ou un système de niveau 1 est
un changement majeur, sauf impossibilité imposée par un fournisseur.

## Dépréciation et migration

Toute incompatibilité planifiée suit cette procédure :

1. ajouter l'alternative et documenter l'ancienne surface comme dépréciée ;
2. produire un avertissement actionnable lorsque le compilateur peut détecter
   l'usage ;
3. conserver l'ancienne surface pendant au moins une version mineure complète ;
4. publier un guide de migration avec exemples avant le retrait ;
5. ajouter les anciens et nouveaux cas à la suite de compatibilité ;
6. retirer seulement dans une version majeure et mentionner le changement en
   tête du changelog.

Le guide doit indiquer la recherche à effectuer, le remplacement, les
différences de comportement et, si possible, une commande automatisée. Une
exception urgente doit être marquée « sécurité » ou « comportement
indéfini », avec une solution de repli.

## Suite de compatibilité N/N+1

Le dépôt contient `tests/compatibility/` et
`tests/compatibility/run_compatibility.cmake`. Pour chaque fixture stable, la
suite :

1. construit la même source avec le dernier Janus publié (N) et le candidat
   (N+1) ;
2. exécute les deux binaires sur la plateforme du candidat ;
3. normalise CRLF vers LF et compare les sorties à un résultat versionné ;
4. échoue si une compilation, une exécution ou une sortie diverge.

CTest exécute le harnais avec le compilateur courant des deux côtés afin de
détecter en continu une fixture ou un harnais cassé. Avant une release 1.x, la
validation doit aussi fournir le binaire de la dernière version publiée.

La matrice de release complète ajoute :

- un projet avec manifeste et lockfile de format précédent, reconstruit avec
  `--locked --offline` ;
- la fixture C compilée avec les en-têtes système de N+1 et liée à un objet
  produit par N ;
- l'exécution des binaires autonomes produits par N ;
- les trois plateformes de niveau 1.

Une modification d'une sortie attendue exige soit une correction démontrant
que l'ancien comportement violait déjà le contrat, soit une dépréciation et
une version majeure.
