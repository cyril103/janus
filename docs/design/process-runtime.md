# Arguments, environnement et processus

Statut : implémenté pour Janus 0.7.1.

Le module `std.process` expose les arguments du programme, les variables
d’environnement et l’exécution synchrone ou interactive d’un enfant. Toutes les opérations
natives utilisent UTF-8 à la frontière Janus et retournent les échecs
récupérables dans `Result[_, SystemError]`.

## Arguments du programme

`programArgumentCount` inclut l’argument zéro, qui désigne l’exécutable.
`programArgument(index)` retourne une vue empruntée dans une `Option`. Ces vues
restent valides pendant toute la durée du programme et ne doivent pas être
libérées.

Le backend transmet le vrai `argc/argv` au runtime avant les initialisations
globales. Sur Windows, le runtime relit la ligne de commande Unicode native et
la découpe selon les règles de `CommandLineToArgvW`, puis convertit chaque
argument strictement en UTF-8. Les espaces et caractères Unicode ne sont donc
ni perdus ni réinterprétés.

## Environnement

`environmentVariable` valide le nom, refuse NUL et `=`, puis retourne
`Ok(Some(EnvironmentValue))` avec une copie propriétaire, `Ok(None)` si la
variable est absente, ou `Error(SystemError)` sur échec. La vue de
`EnvironmentValue.view` reste valide jusqu’à la destruction de la valeur.
Windows utilise les API larges ; POSIX copie la valeur UTF-8.

`currentWorkingDirectory` capture de la même manière le répertoire courant dans
une `WorkingDirectory` propriétaire. Sa vue reste stable jusqu’à la destruction
de la valeur.

## Lancement sans shell

`runProcess` reçoit séparément l’exécutable et un `Array[string]` d’arguments.
La chaîne vide comme répertoire de travail signifie « hériter du répertoire
courant ». Aucun shell, développement de variable, glob ou interprétation de
métacaractère n’intervient.

L’appel est synchrone. Il draine stdout et stderr en parallèle, attend toujours
l’enfant et retourne un `ProcessResult` propriétaire. Le code de sortie est
disponible par `exitCode`. Les captures sont des octets
(`stdoutData`/`stdoutSize` et `stderrData`/`stderrSize`) et ne sont jamais
supposées UTF-8 implicitement.

Une commande absente, un refus d’accès, un répertoire invalide, un argument
invalide ou un échec de création retournent `Error(SystemError)`. Après le
retour, succès ou échec, tous les handles sont fermés et aucun enfant ne
subsiste.

## Processus interactif

`spawnProcess` lance l’enfant sans attendre sa terminaison et retourne un
`ChildProcess`. Son entrée et sa sortie standard sont reliées à des tubes :
`write`/`writeText` transmettent une requête complète, `read` attend des octets
ou la fin du flux, et `closeInput` signale explicitement EOF à l’enfant. La
sortie d’erreur est héritée du parent afin qu’un tube non drainé ne puisse pas
bloquer le processus.

Les appels de lecture sont bloquants. Le destructeur ferme les tubes, récupère
le processus déjà terminé ou le termine encore actif, puis libère tous les
handles. Cette API convient notamment aux protocoles requête-réponse persistants
comme LSP ; les applications graphiques doivent éviter de lire sans avoir envoyé
une requête qui garantit une réponse.

La révision 0.7.4 mutualise la construction des erreurs d'épuisement mémoire
sans réduire leur diagnostic : l'opération reste « process.run », la catégorie
`ResourceExhausted`, le code synthétique zéro et le contexte est
l'exécutable. Les erreurs natives conservent leur code non nul et le contexte
fourni. La surface publique est inchangée.

## Validation

La fixture `tests/fixtures/runtime/processes.janus` se relance elle-même avec
un argument contenant des espaces et un argument Unicode. Elle vérifie
l’environnement, le répertoire de travail, stdout/stderr, le code de sortie,
la commande absente, le refus d’exécution et le nettoyage. AddressSanitizer
reste actif ; seule sa détection de fuites est désactivée pour cette fixture,
car deux runtimes LeakSanitizer parent/enfant avec flux capturés peuvent
attendre la fermeture mutuelle des descripteurs. Les allocations, handles et
attentes d’enfant sont aussi exercés par le test runtime natif, qui vérifie un
aller-retour interactif avant la fermeture de l’entrée.

La fixture 0.7.4 compare les quatre champs de `SystemError` pour une commande
absente et un exécutable refusé, en plus des chemins succès et Unicode.
