# Déroulement portable des paniques

Statut : implémenté pour Janus 0.24.0.

## Contrat

Une `panic` termine toujours le processus, mais elle exécute auparavant tous
les `defer` et destructeurs actifs des cadres Janus traversés. L'ordre est LIFO
dans une portée, entre les portées d'une fonction, puis entre les fonctions.
Chaque action est retirée de la pile avant son exécution : si un destructeur
déclenche une seconde `panic`, le nouveau diagnostic est ajouté et le
déroulement reprend avec l'action suivante, sans répétition de celle qui a
échoué.

Les retours normaux, `return`, `break`, `continue` et la propagation par `?`
gardent leur nettoyage statique existant. Une callback synchrone et un appel
FFI effectué par du code Janus participent au même déroulement. Une fonction
étrangère qui appelle `janus_panic` sur le même thread est donc couverte ; une
sortie étrangère qui contourne cette fonction (par exemple `longjmp`, exception
C++ ou terminaison du thread) ne fait pas partie de l'ABI Janus. Une panique ne
se propage jamais d'un thread à un autre.

Pendant l'initialisation globale, seuls les objets dont l'initialisation est
achevée sont finalisés. Après les cadres locaux, les finaliseurs de modules
s'exécutent dans l'ordre inverse des initialisations. Leur garde rend une
seconde entrée inoffensive.

## ABI runtime

Le backend alloue sur la pile native un enregistrement de trois pointeurs :

```c
struct JanusPanicCleanupFrame {
    struct JanusPanicCleanupFrame *previous;
    void (*cleanup)(void *);
    void *context;
};
```

`janus_push_panic_cleanup(frame, cleanup, context)` empile cet enregistrement
dans une liste propre au thread. `janus_pop_panic_cleanup(frame)` le retire au
retour normal. Avant tout appel direct, indirect, méthode, extension, indexeur,
itérateur ou FFI, le backend empile une entrée distincte pour chaque nettoyage
actif de l'appelant. Un site direct de `panic` fait de même pour son propre
cadre. `janus_panic_with_context` détache chaque entrée avant d'appeler son
thunk, puis lance les finaliseurs globaux et `abort`.

Ce trampoline en C11 n'emploie ni exceptions natives ni landing pads LLVM. Il
a donc le même contrat sous Linux, macOS et Windows. La collecte d'une trace
native complète reste disponible seulement sur les plateformes où
`backtrace` existe ; les modes `short` et `off`, l'origine de la panique et les
nettoyages ne dépendent pas de cette disponibilité.

## Limites

Les callbacks doivent rester synchrones : conserver un pointeur vers une
callback ou son contexte après le retour de l'appel est déjà hors du contrat
d'emprunt. Une panique dans un finaliseur global interrompt les finaliseurs
globaux suivants ; la garde empêche leur double exécution. Les cadres Janus
locaux ont alors déjà tous été déroulés.
