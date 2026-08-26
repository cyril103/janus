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
supposées UTF-8 implicitement. Les pointeurs retournés sont explicitement
empruntés au `ProcessResult` et deviennent invalides à sa destruction.

Une commande absente, un refus d’accès, un répertoire invalide, un argument
invalide ou un échec de création retournent `Error(SystemError)`. Après le
retour, succès ou échec, tous les handles sont fermés et aucun enfant ne
subsiste.

## Processus interactif

`spawnProcess` lance l’enfant sans attendre sa terminaison et retourne un
`ChildProcess`. Son entrée et sa sortie standard sont reliées à des tubes :

- `tryRead` et `tryWrite` ne bloquent jamais et signalent l’absence de
  progression immédiate par `SystemErrorCategory.WouldBlock` ; `tryRead`
  réserve `Ok(0)` à la fin du flux et les opérations de taille nulle réussissent ;
- `read` et `write` conservent leur comportement bloquant historique. Sur
  POSIX, les extrémités parent sont non bloquantes et ces méthodes attendent
  leur disponibilité avec `poll`, sans boucle active. Sur Windows, stdout reste
  un tube anonyme synchrone sondé par `PeekNamedPipe`. Stdin est un named pipe
  byte en mode `PIPE_NOWAIT` : `tryWrite` écrit directement et traduit un état
  transitoire sans progression en `WouldBlock`, tandis que `write` réessaie.
- `terminate` envoie `SIGKILL` ou appelle `TerminateProcess` puis retourne sans
  attendre. `tryWait` emploie la sonde non bloquante native de chaque plateforme
  et retourne `None` tant que l’enfant est actif,
  puis `Some(exitCode)`. Sur POSIX, le premier succès récupère l’enfant et
  mémorise son statut afin que les appels suivants et le destructeur ne le
  récupèrent ni ne le signalent une seconde fois.

Comme les autres objets propriétaires Janus, `ChildProcess` est mono-thread et
non thread-safe : aucune autre thread ne peut effectuer une entrée-sortie sur
ses handles.

`write`/`writeText` transmettent une requête complète, `read` attend des octets
ou la fin du flux, et `closeInput` signale explicitement EOF à l’enfant. La
sortie d’erreur est héritée du parent afin qu’un tube non drainé ne puisse pas
bloquer le processus.

Sans appel préalable à `terminate`, le destructeur historique ferme les tubes,
récupère le processus déjà terminé ou le termine encore actif, puis libère tous
les handles. Dès qu’un appel à `terminate` est tenté, y compris si l’OS refuse
la demande, sa stratégie de repli est au contraire strictement bornée : sur
POSIX, il tente une récupération immédiate non bloquante mais ne renvoie aucun
signal et n’attend pas si l’enfant
est encore actif (un zombie peut donc subsister jusqu’à la fin du parent) ; sur
Windows, il ferme les handles sans attente infinie. Un appel intermédiaire à
`tryWait` reste possible pour récupérer et mémoriser le statut, mais n’est pas
nécessaire avant la destruction. Cette API convient notamment aux protocoles
requête-réponse persistants comme LSP.

La révision 0.7.4 mutualise la construction des erreurs d'épuisement mémoire
sans réduire leur diagnostic : l'opération reste « process.run », la catégorie
`ResourceExhausted`, le code synthétique zéro et le contexte est
l'exécutable. Les erreurs natives conservent leur code non nul et le contexte
fourni.

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
