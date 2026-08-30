# Inventaire de stabilité courant

Baseline : Janus 0.22 et changements non publiés du 30 août 2026.

Cet inventaire est la source de vérité pré-1.0. Les versions historiques sont
conservées séparément. Les statuts signifient :

- `stable-candidate` : surface proposée pour le contrat 1.0 ;
- `experimental` : surface publique susceptible de changer avant promotion ;
- `internal-detail` : détail visible mais explicitement hors contrat public.

La CI vérifie que chaque groupe de la
[surface publique](public-surface-0.5.json) possède une décision. Une promotion
requiert une fixture de compatibilité et au moins un consommateur aval. Un
retrait requiert une note de migration.

## Langage

| Surface | Statut | Décision courante |
| --- | --- | --- |
| `syntax.declarations` | `stable-candidate` | Cœur 1.0. |
| `syntax.control-flow` | `stable-candidate` | Cœur 1.0. |
| `syntax.generics-traits` | `stable-candidate` | Inclut types associés ; les types de genre supérieur restent hors surface. |
| `syntax.modules-visibility` | `stable-candidate` | Cœur 1.0. |
| `syntax.ownership` | `stable-candidate` | Inclut emprunts lexicaux, `scoped`, `move`, `consume` et `defer`. |
| `semantics.numeric` | `stable-candidate` | Largeurs et conversions explicites conservées. |
| `semantics.ownership-cleanup` | `stable-candidate` | Destruction exactement une fois. |
| `semantics.errors-panics` | `stable-candidate` | Comportement couvert, texte exact exclu. |
| `semantics.constant-evaluation` | `stable-candidate` | Parité d'exécution ou rejet. |

## Projet, résolution et ABI

| Surface | Statut | Décision courante |
| --- | --- | --- |
| `manifest.package` | `stable-candidate` | Cœur 1.0. |
| `manifest.dependencies.path` | `stable-candidate` | Cœur 1.0. |
| `manifest.dependencies.git` | `stable-candidate` | Révisions immuables. |
| `manifest.dependencies.registry` | `experimental` | Promotion après deux cycles d'exploitation réels. |
| `lockfile.format-v1` | `stable-candidate` | Format versionné. |
| `lockfile.locked` | `stable-candidate` | Gate de reproductibilité. |
| `lockfile.offline` | `stable-candidate` | Gate hors réseau. |
| `resolver.path` | `stable-candidate` | Cœur 1.0. |
| `resolver.git` | `stable-candidate` | Cœur 1.0. |
| `resolver.registry` | `experimental` | Même gate que le registre. |
| `c-abi.extern-def` | `stable-candidate` | ABI C documentée. |
| `c-abi.scalars` | `stable-candidate` | Représentations documentées. |
| `c-abi.pointers` | `stable-candidate` | Contrats `borrow`, `consume` et `owned`. |
| `c-abi.variadics` | `stable-candidate` | Promotions C documentées. |
| `protocol.registry-v1` | `experimental` | Protocole versionné mais pas encore assez exploité. |

## CLI, outils et distribution

| Surface | Statut | Décision courante |
| --- | --- | --- |
| `cli.new` | `stable-candidate` | Cœur 1.0. |
| `cli.init` | `stable-candidate` | Cœur 1.0. |
| `cli.add` | `stable-candidate` | Chemins et Git stables, registre expérimental. |
| `cli.remove` | `stable-candidate` | Cœur 1.0. |
| `cli.search` | `experimental` | Dépend du registre. |
| `cli.publish` | `experimental` | Dépend du registre. |
| `cli.clean` | `stable-candidate` | Cœur 1.0. |
| `cli.check` | `stable-candidate` | Cœur 1.0. |
| `cli.build` | `stable-candidate` | Cœur 1.0. |
| `cli.run` | `stable-candidate` | Cœur 1.0. |
| `cli.test` | `stable-candidate` | Tests et doctests. |
| `cli.fmt` | `stable-candidate` | Format déterministe. |
| `cli.doc` | `stable-candidate` | Index hors ligne. |
| `cli.--help` | `stable-candidate` | Présence garantie, mise en page exclue. |
| `cli.--version` | `stable-candidate` | Identité de build garantie. |
| `package.archives` | `stable-candidate` | Archives tier-1 attestées. |
| `package.janusup` | `stable-candidate` | Canaux et vérification de provenance. |
| `tooling.janus-lsp` | `experimental` | Promotion après budgets et cancellation. |
| `tooling.vscode` | `experimental` | Promotion après matrice de compatibilité et publication. |

## Bibliothèque standard

| Surface | Statut | Décision courante |
| --- | --- | --- |
| `std.array` | `stable-candidate` | Cœur collections. |
| `std.array_builder` | `stable-candidate` | Cœur collections. |
| `std.builder` | `stable-candidate` | Cœur texte. |
| `std.bytes` | `stable-candidate` | Vues bornées et décodage vérifié. |
| `std.c` | `stable-candidate` | Interopérabilité. |
| `std.deque` | `experimental` | Observation aval requise. |
| `std.error` | `stable-candidate` | Taxonomie commune. |
| `std.graphics` | `experimental` | Famille officielle hors cœur 1.0. |
| `std.graphics.audio` | `experimental` | Paquet graphique futur. |
| `std.graphics.drawing` | `experimental` | Paquet graphique futur. |
| `std.graphics.input` | `experimental` | Paquet graphique futur. |
| `std.graphics.resources` | `experimental` | Paquet graphique futur. |
| `std.graphics.types` | `experimental` | Paquet graphique futur. |
| `std.fs` | `stable-candidate` | Cœur système. |
| `std.functional` | `experimental` | Valider la surface après usage aval. |
| `std.hashing` | `stable-candidate` | Cœur collections. |
| `std.hashmap` | `stable-candidate` | Cœur collections. |
| `std.hashset` | `stable-candidate` | Cœur collections. |
| `std.io` | `stable-candidate` | Cœur système. |
| `std.iterator` | `stable-candidate` | Cœur collections. |
| `std.math` | `stable-candidate` | Cœur numérique. |
| `std.numeric` | `stable-candidate` | Conversions explicites auditées. |
| `std.option` | `stable-candidate` | Cœur erreurs. |
| `std.ordering` | `stable-candidate` | Contrats des collections. |
| `std.path` | `stable-candidate` | Cœur système. |
| `std.persistent_list` | `experimental` | Première collection persistante. |
| `std.priority_queue` | `experimental` | Observation aval requise. |
| `std.random` | `stable-candidate` | Cœur système. |
| `std.process` | `stable-candidate` | Cœur système. |
| `std.range` | `stable-candidate` | Cœur itération. |
| `std.result` | `stable-candidate` | Cœur erreurs. |
| `std.shared` | `experimental` | Modèle de partage récent. |
| `std.slice` | `stable-candidate` | Élément du modèle d'emprunt 1.0. |
| `std.system` | `stable-candidate` | Cœur système. |
| `std.text` | `stable-candidate` | Cœur texte. |
| `std.testing` | `stable-candidate` | Harnais natif nécessaire à l'écosystème. |
| `std.time` | `stable-candidate` | Cœur système. |
| `std.validated` | `experimental` | Abstraction applicative récente. |
| `std.wall_time` | `stable-candidate` | Horloge monotone. |

## Conditions de gel

Les gates exécutables et leurs preuves sont définies dans la
[roadmap 1.0](roadmap-1.0.md). Aucune surface expérimentale n'est promue par
simple ancienneté et aucune fonctionnalité majeure nouvelle n'entre dans le
cœur avant la première RC.
