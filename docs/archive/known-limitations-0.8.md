# Limites connues de Janus 0.8

> Document historique. Pour l'état courant, consulter
> l'[audit technique 0.17](../audit-0.17.md).

Janus 0.8 reste une version pré-1.0. Les surfaces ci-dessous sont conservées
pour expérimentation, mais ne font pas partie du candidat stable.

- `manifest.dependencies.registry`, `resolver.registry`, `cli.search` et
  `cli.publish` : le protocole v1 est versionné, mais le client, la politique de
  cache et l'expérience de publication distante nécessitent davantage de recul.
- `tooling.janus-lsp` et `tooling.vscode` : une matrice de compatibilité est
  publiée, mais les garanties comportementales et l'interface éditeur peuvent
  encore évoluer avant leur gel explicite dans le contrat 1.x.
- `std.graphics`, `std.graphics.audio`, `std.graphics.drawing`,
  `std.graphics.input`, `std.graphics.resources` et `std.graphics.types` :
  l'API dépend de raylib et sa portabilité, sa gestion de ressources et sa
  couverture tier-1 ne satisfont pas encore le niveau du noyau de la stdlib.
- `std.testing` : les métadonnées, le schéma JSON et la représentation JUnit
  sont versionnés pour l’expérimentation, mais restent hors du gel 1.0 initial.
- `std.validated` : l'accumulation applicative d'erreurs a été ajoutée après la
  baseline 0.8 et reste expérimentale jusqu'à la stabilisation de son API.
- `std.numeric` : les politiques de conversion sont entièrement spécifiées et
  testées, mais leur nouvelle surface reste expérimentale jusqu'au prochain
  inventaire de stabilité.
- `std.slice` : les vues contiguës empruntées ont été ajoutées après la baseline
  0.8 et restent expérimentales jusqu'à leur validation dans un inventaire
  courant.
- `std.priority_queue` : la file de priorité stable a été ajoutée après la
  baseline 0.8 et reste expérimentale jusqu'à la stabilisation de son API.
- `std.bytes`, `std.deque`, `std.error` et `std.ordering` : ces vues binaires,
  collections et contrats partagés ont été ajoutés après la baseline 0.8 et
  restent expérimentaux jusqu'à leur prochain inventaire de stabilité.

Autres limites hors surface promise : format et ordre exacts des diagnostics,
ABI des agrégats Janus, objets compilés entre plateformes, performances,
contenu du cache et API C++ internes du compilateur. Elles sont détaillées dans
le [contrat proposé](../stability-contract.md).
