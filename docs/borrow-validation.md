# Validation du système d'emprunts

Le modèle d'emprunts lexicaux est validé à quatre niveaux complémentaires.
Cette matrice est le gate de non-régression associé à l'issue #265.

| Garantie | Validation principale |
|---|---|
| emprunts partagés, méthodes `borrow def`, branches et boucles | `language.immutable_borrow` |
| exclusivité mutable, réemprunts et champs `borrow var` | `language.mutable_borrow` |
| destruction, mutation, réallocation, stockage et retours | `language.borrow_invalidation` |
| appels imbriqués et closures bornées ou échappantes | `language.borrowed_calls_closures` |
| conteneurs contigus partagés et mutables | `runtime.slices`, `runtime.slice_out_of_bounds` |
| nettoyage lors d'une panique | `runtime.borrow_panic_cleanup` sous AddressSanitizer |
| codes et messages CLI | `diagnostics.invalid_corpus` |
| application multi-module réelle | canari aval Janus Studio épinglé |

Les violations sont réparties entre `JANA0024` et `JANA0028` : conflit,
invalidation, échappement, accès interdit et source invalide. Le corpus invalide
verrouille chaque code et un fragment de message. Les diagnostics d'invalidation
ajoutent aussi une note indiquant quelle portée terminer avant l'opération.

Les fixtures runtime sont compilées nativement avec AddressSanitizer et la
détection de fuites sur Linux. Les tests normaux, les bornes invalides et une
panique avec nettoyages différés couvrent l'absence de lecture après libération,
de double destruction et de fuite observable.

Les workflows stable et nightly extraient uniquement l'archive candidate, puis
exécutent les gates Janus8 et Janus Studio avant publication. Studio doit passer
le formatage, l'analyse complète, ses tests release et son build release. Son
avertissement `JANA0014` connu reste documenté dans le contrat du canari avant
l'activation future de `--deny-warnings`.
